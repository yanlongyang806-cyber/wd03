import cryptic
import sys

def CrypticTrace(frame, event, arg):
    if event == 'line':
        return

    co = frame.f_code
    f_fn = co.co_filename.replace(cryptic.__file__, '')
    f_name = co.co_name

    caller = frame.f_back
    if caller:
        c_fn = caller.f_code.co_filename.replace(cryptic.__file__, '')
        c_line = caller.f_lineno
    else:
        c_fn = ''
        c_line = 0

    if event[-4:] == 'call':
        cryptic.profile_begin(f_fn, f_name, c_fn, c_line)
        return CrypticTrace
    else:
        cryptic.profile_end(f_fn, f_name, c_fn, c_line)

sys.settrace(CrypticTrace)
