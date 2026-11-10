import re,sys
lines=[re.sub(r'^.*?\d{4}-\d\d-\d\dT[\d:.]+Z ?','',l.rstrip('\n')) for l in open(sys.argv[1],errors='replace')]
names=set()
for i,l in enumerate(lines):
    if l.endswith(': FAILED:'):
        # walk back to the test-case name: line after a dashed separator
        j=i-1
        while j>0 and not lines[j].startswith('-----'): j-=1
        # header block: ----- / name / ----- / file:line
        k=j-1
        while k>0 and not lines[k].startswith('-----'): k-=1
        name=' / '.join(x.strip() for x in lines[k+1:j] if x.strip())
        names.add(name)
for n in sorted(names): print(n)
print(len(names),'distinct failing cases', file=sys.stderr)
