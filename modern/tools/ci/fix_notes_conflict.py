import re
p='modern/vt-modern-development-notes.md'
s=open(p).read()
while True:
    m=re.search(r'<<<<<<< [^\n]*\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n',s,re.S)
    if not m: break
    ours,theirs=m.group(1),m.group(2)
    ents=re.split(r'(?=^### )',theirs,flags=re.M)
    new=''.join(e for e in ents if e.strip() and e.split('\n')[0] not in ours)
    if new and not new.endswith('\n\n'): new=new.rstrip('\n')+'\n\n'
    s=s[:m.start()]+new+ours+s[m.end():]
open(p,'w').write(s)
