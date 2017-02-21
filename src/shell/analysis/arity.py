#-*- coding: utf-8 -*-

from datetime import datetime
import re
from analysis import Analysis
from src.shell.parser.arity import ArityLogParser

class ArityAnalysis(Analysis):

    def __init__(self, pgm, logfile, data=None):
        Analysis.__init__(self, pgm, logfile)

        self.data = data
        if data == None:
            self.protos = None
        else:
            self.protos = data.protos

    def parse_log(self):
        self.log = ArityLogParser(self.logfile)
        return

    def check_one_arity(self, fname, ar, proto):
        return ar == len(proto) - 1

    def check_one_ret(self, fname, ret, proto):
        return (ret > 0) == (proto[0] != "void")

    def print_general_info(self):
        if self.data is None:
            Analysis.print_general_info(self)
        else:
            Analysis.print_general_info_with_data(self, self.data)

    def display(self):
        for fn, ar in self.log.get():
            print "{}: {} -> {}".format(fn, sum([int(x) for x in ar[0:-1]]), ar[-1])
        print("")
        self.print_general_info()

    def accuracy(self, get=False, verbose=True):
        if verbose:
            self.print_general_info()
            print("")

        without_name = 0
        variadic = 0
        pseudo_functions = 0
        not_found = 0

        total = 0
        ok_ar = 0
        ok_ret = 0
        for function, arity in self.log.get():
            fn = function.split(":")[-1]
            int_ar, int_stack_ar, float_ar, float_stack_ar, ret = arity
            ar = int_ar + int_stack_ar + float_ar + float_stack_ar
            if fn == '':
                without_name += 1
                continue
            elif self.is_pseudo_function(fn):
                pseudo_functions += 1
                continue
            elif fn not in self.protos.keys():
                not_found += 1
                continue

            proto = self.protos[fn]
            if self.is_variadic(proto):
                variadic += 1
                continue

            total += 1
            if self.check_one_arity(fn, ar, proto):
                ok_ar += 1

            if self.check_one_ret(fn, ret, proto):
                ok_ret += 1

        if verbose:
            print("Ignored")
            print("| Without name:            {0}".format(without_name))
            print("| Variadic:                {0}".format(variadic))
            print("| Pseudo-Functions:        {0}".format(pseudo_functions))
            print("- Not in binary/source:    {0}".format(not_found))
            print("")

            print("Accuracy of inference")
            print("| Params Ok/Total tested:  {0}/{1}".format(ok_ar, total))
            print("| Return Ok/Total tested:  {0}/{1}".format(ok_ret, total))
            print("| Ratio params:            {0:.2f}%".format(self.ratio(ok_ar, total)))
            print("- Ratio return:            {0:.2f}%".format(self.ratio(ok_ret, total)))

        if get:
            return (ok_ar + ok_ret, 2*total)

    def mismatch(self):
        self.print_general_info()
        print("")

        for function, arity in self.log.get():
            fn = function.split(":")[-1]
            img = function.split(":")[0]
            imgaddr = int(function.split(":")[1])
            int_ar, int_stack_ar, float_ar, float_stack_ar, ret = arity
            ar = int_ar + int_stack_ar + float_ar + float_stack_ar
            if fn == "" or fn not in self.protos.keys():
                continue
            proto = self.protos[fn]
            if self.is_variadic(proto):
                continue

            arity_ok = self.check_one_arity(fn, ar, proto);
            return_ok = self.check_one_ret(fn, ret, proto);

            if arity_ok and return_ok:
                continue

            print("[{}@{}] {} ({}) -> {}".format(img, hex(imgaddr),
                    fn, ", ".join(proto[1:]), proto[0]))
            if not arity_ok:
                print("   Arity  : Expected {} got {}".format(len(proto) - 1, ar))
            if not return_ok:
                if ret:
                    print("   Return : Expected 0 got 1")
                else:
                    print("   Return : Expected 1 got 0")

