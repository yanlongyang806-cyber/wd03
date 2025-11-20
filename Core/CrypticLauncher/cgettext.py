import sys
import re
import os
import os.path

memo = set()

extract_re = re.compile(
r'''_\(\s*                     # The opening 
       (                       # Capture all strings
        (?:                    # One or more with space between them
         "(?:[^"\\]*(?:\\.[^"\\]*)*)" # Quoted string possibly with embedded escape characters
        \s*)+                  # /one-or-more
       )                       # /capture
      \)''', re.X)             # End of the macro

parts_re = re.compile(r'"(?:[^"\\]*(?:\\.[^"\\]*)*)"')
nonalnum_re = re.compile(r'[^a-zA-Z0-9]')

def fixup_string(s):
    parts = parts_re.finditer(s)
    s = ''.join(p.group(0)[1:-1] for p in parts)
    return s.decode('string_escape')
    
def superesc(s):
    return nonalnum_re.sub(lambda md: md.group(0)==' ' and '_' or 'Q%03u'%ord(md.group(0)), s.replace('Q', 'QQ'))

def msnode(s, line=None, scope=''):
    if line:
        line = 'Description <&%s&>\n    '%line
    else:
        line = ''
    msg_key = 'IDS_'+superesc(s)
    if msg_key in memo:
        return ''
    memo.add(msg_key)
    return """Message
{
    MessageKey %s
    %sScope "CrypticLauncher%s"
    DefaultString <&%s&>
}

"""%(msg_key, line, scope, s)

def process_file(filename, with_source=False, echo=False, manual_strings=False):
    print 'Generating .ms file for %s'%filename
    f = open(filename)
    msdata = []
    if manual_strings:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            elif line.startswith('@'):
                msdata.append(msnode(line[1:], scope='NoDat'))
            else:
                msdata.append(msnode(line))
    else:
        code = f.read()
        for md in extract_re.finditer(code):
            pre = code[:md.start(0)]
            pre = pre[pre.rfind('\n')+1:]
            post = code[md.end(0):]
            post = post[:post.find('\n')]
            line = pre + md.group(0) + post
            if line.strip().startswith('//'):
                continue
            msdata.append(msnode(fixup_string(md.group(1)), with_source and line.strip()))
    msdata = ''.join(msdata)
    msfile = os.path.join(os.path.dirname(filename), 'ms', os.path.basename(filename)+'.ms')
    if not msdata.strip():
        if os.path.exists(msfile):
            os.unlink(msfile)
        return
    if echo:
        print msdata

    if not os.path.isdir(os.path.dirname(msfile)):
        os.makedirs(os.path.dirname(msfile))
    f = open(msfile, 'w')
    f.write(msdata)
    

def main(argv):
    for name in argv:
        if os.path.isdir(name):
            for dirpath, dirname, filenames in os.walk(name):
                if dirpath.endswith('.svn') or dirpath.endswith('AutoGen'): continue
                for filename in filenames:
                    if filename.endswith('.c'):
                        process_file(os.path.join(dirpath, filename))
                    elif filename == 'manualstrings.txt':
                        process_file(os.path.join(dirpath, filename), manual_strings=True)
        elif os.path.isfile(name):
            process_file(name)
        else:
            print 'Unknown argument "%s"'%name

if __name__ == '__main__':
    if len(sys.argv) > 1:
        main(sys.argv[1:])
    else:
        main([os.path.dirname(os.path.abspath(__file__))])