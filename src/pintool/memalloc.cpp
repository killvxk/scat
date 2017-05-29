#include <list>
#include <map>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>

#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>

#include "pin.H"

#include "utils/debug.h"
#include "utils/functions_registry.h"
#include "utils/hollow_stack.h"
#include "log/ftable.h"

#define NB_FN_MAX               100000
#define MAX_DEPTH               1000
#define NB_VALS_TO_CONCLUDE     500
#define NB_CALLS_TO_CONCLUDE    500
#define SEUIL                   0.8
#define IGNORE_LIBRARIES        1
#define COUPLE_THRESHOLD        0.75

ifstream ifile;
KNOB<string> KnobInputFile(KNOB_MODE_WRITEONCE, "pintool", "i", "stdin", "Specify an intput file");
ofstream ofile;
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "stdout", "Specify an output file");

/* Call stack */
HollowStack<MAX_DEPTH, FID> call_stack;
/* Call stack is jump */
HollowStack<MAX_DEPTH, bool> is_jump_stack;

UINT64 counter;

struct timeval start, stop; 

unsigned int nb_fn = 0;

typedef struct {
    ADDRINT val;
    UINT64 fid;
    UINT64 caller;
    UINT64 counter;
    UINT32 pos;
    BOOL from_main;
} param_t;

unsigned int *nb_p;
bool **param_addr;
bool *is_instrumented;
list<param_t *> *param;

bool init = false;

string read_part(char* c) {
    char m;
    string str = "";

    ifile.read(&m, 1);
    while (ifile && m != ':' && m != '\n') {
        str += m;
        ifile.read(&m, 1);
    }

    *c = m;
    return str;
}

ADDRINT val_from_reg(CONTEXT *ctxt, unsigned int pid) {

    trace_enter();

    REG reg;
    switch (pid) {
    case 0:
        reg = REG_RAX;
        break;
    case 1:
        reg = REG_RDI;
        break;
    case 2:
        reg = REG_RSI;
        break;
    case 3:
        reg = REG_RDX;
        break;
    case 4:
        reg = REG_RCX;
        break;
    case 5:
        reg = REG_R8;
        break;
    case 6:
        reg = REG_R9;
        break;
    default:

        trace_leave();

        return 0;
    }

    trace_leave();

    return PIN_GetContextReg(ctxt, reg);
}


VOID fn_call(CONTEXT *ctxt, FID fid, bool is_jump, ADDRINT inst_addr) {
    
    trace_enter();

    FID caller = call_stack.top();
    call_stack.push(fid);
    is_jump_stack.push(is_jump);
    counter += 1;

    bool param_pushed = false;

    if (!is_instrumented[fid]) {
        trace_leave();
        return;
    }

    for (unsigned int i = 1; i <= nb_p[fid]; i++) {
        if (param_addr[fid][i]) {
            param_t *new_param = (param_t *) malloc(sizeof(param_t));
            new_param->fid = fid;
            new_param->caller = caller;
            new_param->counter = counter;
            new_param->val = val_from_reg(ctxt, i); 
            new_param->pos = i;
            if (IMG_Valid(IMG_FindByAddress(inst_addr))) {
                if (IMG_IsMainExecutable(IMG_FindByAddress(inst_addr)))
                new_param->from_main = IMG_IsMainExecutable(IMG_FindByAddress(inst_addr));
            } else {
                new_param->from_main = 1;
            }
            param->push_front(new_param);
            param_pushed = true;
        }
    }

    /* If the function is instrumented (ie for instance has an ADDR as
       a return value) AND was not logged yet, create a special
       entry to log the date of call */
    if (!param_pushed) {
        param_t *new_addr = (param_t *) malloc(sizeof(param_t));
        new_addr->fid = fid;
        new_addr->caller = caller;
        new_addr->counter = counter;
        new_addr->val = 0; 
        new_addr->pos = 99;
        if (IMG_Valid(IMG_FindByAddress(inst_addr))) {
            new_addr->from_main = IMG_IsMainExecutable(IMG_FindByAddress(inst_addr));
        } else {
            new_addr->from_main = 0;
        }
        param->push_front(new_addr);
    }

    trace_leave();
    return;
}

VOID fn_icall(CONTEXT* ctxt, ADDRINT target, bool is_jump, ADDRINT inst_addr) {
    
    trace_enter();

    // Indirect call, we have to look up the function each time
    // The functions `fn_lookup` & `fn_register` needs PIN's Lock.
    // Locking is not implicit in inserted call, as opposed
    // to callback added with *_AddInstrumentFunction().
    PIN_LockClient();
    FID fid = fn_lookup_by_address(target);
    if (is_jump && fid == FID_UNKNOWN) {
        trace_leave();
        return;
    }
    PIN_UnlockClient();

    fn_call(ctxt, fid, is_jump, inst_addr);

    trace_leave();
    return;
}

VOID fn_ret(CONTEXT *ctxt, UINT32 fid) {
    trace_enter();
    
    counter += 1;

    if (!call_stack.is_top_forgotten()) {
        while (is_jump_stack.top()) {
            FID fid = call_stack.top();
            call_stack.pop();
            is_jump_stack.pop();
            FID caller = call_stack.top();
            if (is_instrumented[fid]) {
                param_t *new_ret = (param_t *) malloc(sizeof(param_t));
                new_ret->fid = fid;
                new_ret->counter = counter;
                new_ret->caller = caller; 
                if (param_addr[fid][0])
                    new_ret->val = val_from_reg(ctxt, 0); 
                else
                    new_ret->val = 1;
                new_ret->pos = 0;
                param->push_front(new_ret);
            }
        }
        FID fid = call_stack.top();
        call_stack.pop();
        is_jump_stack.pop();
        FID caller = call_stack.top();
        if (is_instrumented[fid]) {
            param_t *new_ret = (param_t *) malloc(sizeof(param_t));
            new_ret->fid = fid;
            new_ret->counter = counter;
            new_ret->caller = caller; 
            new_ret->val = val_from_reg(ctxt, 0); 
            new_ret->pos = 0;
            param->push_front(new_ret);
        }
    }

    trace_leave();
    return;
}


void fn_registered(
                    FID fid, 
                    unsigned int nb_param, 
                    vector<bool> type_param
                ) {

    trace_enter();

    /* Set the number of parameters */
    nb_p[fid] = nb_param;
    /* Set the array of booleans indicating which parameter is an ADDR */
    param_addr[fid] = (bool *) calloc(nb_p[fid], sizeof(bool));

    /* Is this function instrumented?*/
    is_instrumented[fid] = false;



    if (fid == FID_UNKNOWN) return;

    /* Iteration on parameters */
    for (unsigned int i = 0; i <= nb_p[fid]; i++) {
        if (type_param[i]) {
            param_addr[fid][i] = true;
            is_instrumented[fid] = true;
        }
        else {
            param_addr[fid][i] = false;
        }
    }

    trace_leave();
    return;
}

VOID Commence();

VOID Instruction(INS ins, VOID *v) {
    if (!init)
        Commence();

    ADDRINT inst_addr = INS_Address(ins);

    if (INS_IsCall(ins)) {
        if (INS_IsDirectCall(ins)) {
            ADDRINT addr = INS_DirectBranchOrCallTargetAddress(ins);
            FID fid = fn_lookup_by_address(addr);
            INS_InsertCall(ins, 
                        IPOINT_BEFORE, 
                        (AFUNPTR) fn_call, 
                        IARG_CONST_CONTEXT,
                        IARG_UINT32, fid, 
                        IARG_BOOL, false,
                        IARG_ADDRINT, inst_addr,
                        IARG_END);
        } 
        else {
            INS_InsertCall(ins,
                        IPOINT_BEFORE,
                        (AFUNPTR) fn_icall,
                        IARG_CONST_CONTEXT,
                        IARG_BRANCH_TARGET_ADDR,
                        IARG_BOOL, false,
                        IARG_ADDRINT, inst_addr,
                        IARG_END);
        }
    }

    if (INS_IsIndirectBranchOrCall(ins)) {
        if (!INS_IsCall(ins)) {
            INS_InsertCall(ins,
                    IPOINT_BEFORE,
                    (AFUNPTR) fn_icall,
                    IARG_CONST_CONTEXT,
                    IARG_BRANCH_TARGET_ADDR,
                    IARG_BOOL, true,
                    IARG_ADDRINT, inst_addr,
                    IARG_END);
        }
    }

    if (INS_IsRet(ins)) {
        INS_InsertCall(ins,
                    IPOINT_BEFORE,
                    (AFUNPTR) fn_ret,
                    IARG_CONST_CONTEXT,
                    IARG_END);
    }

    return;
}


VOID Commence() {

    trace_enter();

    /* Init instruction counter */
    counter = 0;
    init = true;
    string _addr, _name;
    if (ifile.is_open()) {
        while (ifile) {
            char m;
            vector<bool> type_param;
            string img_name = read_part(&m);
            if (img_name.empty()) {
                continue;
            }
            ADDRINT img_addr = atol(read_part(&m).c_str());
            string name = read_part(&m);
            type_param.push_back(true);
            FID fid = fn_register(img_name, img_addr, name);
            if (fid != FID_UNKNOWN) {
                fn_registered(fid, 0, type_param);
            }

            type_param.clear();
            img_name = read_part(&m);
            if (img_name.empty()) {
                continue;
            }
            img_addr = atol(read_part(&m).c_str());
            name = read_part(&m);
            float rho = atof(read_part(&m).c_str());
            unsigned char param_pos = atoi(read_part(&m).c_str());

            for (unsigned int pos = 0 ; pos < param_pos ; pos++) {
                type_param.push_back(false);
            }
            type_param.push_back(true);

            if (rho > COUPLE_THRESHOLD) {
                fid = fn_register(img_name, img_addr, name);

                if (fid != FID_UNKNOWN) {
                    fn_registered(fid, param_pos, type_param);
                }
            }

            read_part(&m);
        }
    }

    gettimeofday(&start, NULL);

    trace_leave();

    return;
}

VOID Fini(INT32 code, VOID *v) {

    trace_enter();

    list<param_t *>::reverse_iterator it;

    int depth = 0;
    UINT64 last_date = -1;
    bool is_in = false;

    gettimeofday(&stop, NULL);

    /* First, we log the conversion table fid <-> name */
    log_ftable(ofile);

    it = param->rbegin();

    /* Size of fields of structure */
    ofile << sizeof((*it)->val) << ":";
    ofile << sizeof((*it)->fid) << ":";
    ofile << sizeof((*it)->pos) << ":";
    ofile << sizeof((*it)->counter) << ":";
    ofile << sizeof((*it)->from_main) << endl;

    while (it != param->rend()) {
        param_t *p = *it;
        it++;
        if (last_date != p->counter) {
            if (is_in)
                depth++;
            else
                depth--;
        }
        last_date = p->counter;
        ofile.write((char *) &(p->val), sizeof(p->val)); 
        ofile.write((char *) &(p->fid), sizeof(p->fid));
        ofile.write((char *) &(p->pos), sizeof(p->pos));
        ofile.write((char *) &(p->counter), sizeof(p->counter));
        ofile.write((char *) &(p->from_main), sizeof(p->from_main));
//        std::cerr << p->val << ":" << p->fid << ":" << p->pos << ":" << p->counter << endl;
    }
    ofile.close();

    trace_leave();

    return;
}


int main(int argc, char * argv[]) {
    
    param_addr = (bool **) malloc(NB_FN_MAX * sizeof(bool *));
    is_instrumented = (bool *) calloc(NB_FN_MAX, sizeof(bool));
    nb_p = (unsigned int *) calloc(NB_FN_MAX, sizeof(unsigned int));
    param = new list<param_t *>();

    /* Initialize symbol table code, 
       needed for rtn instrumentation */
    PIN_InitSymbols();
    PIN_SetSyntaxIntel();

    if (PIN_Init(argc, argv)) return 1;

    ifile.open(KnobInputFile.Value().c_str());
    ofile.open(KnobOutputFile.Value().c_str());
    
    // INS_AddInstrumentFunction(Instruction, 0);
    INS_AddInstrumentFunction(Instruction, 0);

    fn_registry_init(NB_FN_MAX);
    vector<bool> unknown_int_idx;
    fn_registered(FID_UNKNOWN, 0, unknown_int_idx);

    /* Register Fini to be called when the 
       application exits */
    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();
    
    return 0;
}


