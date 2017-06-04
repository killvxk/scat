#-*- coding: utf-8 -*-

from .entry import Entry

class ArityEntry(Entry):

    def __init__(self, line, *args, **kwargs):
        l = line[:-1].split(":")
        self._pgm = l[0]
        self._mincalls = int(l[1])
        self._paramth, self._retth = map(lambda a: float(a), l[2:4])
        self._fn_in, self._fp_in, self._tot_in = map(lambda a: int(a), l[4:7])
        self._fn_out, self._fp_out, self._tot_out = map(lambda a: int(a), l[7:])
        super(ArityEntry, self).__init__(*args, **kwargs)

    @property
    def min_calls(self):
        return int(self.__mincalls)
        
    @property
    def param_threshold(self):
        return float(self.__paramth)

    @property
    def ret_threshold(self):
        return float(self.__retth)

    def get(self, param):
        if param == "min_calls":
            return self.min_calls
        elif param == "ret_threshold":
            return self.ret_threshold
        elif param == "param_threshold":
            return self.param_threshold
        else:
            return super(ArityEntry, self).get(param)

