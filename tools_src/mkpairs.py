import os,csv
base=r'C:\skbuild\sd_extract'; out=r'C:\skbuild\sd_named'
MASK=0xEF5D0004; TMP=bytes.fromhex('d7cd735e')
paired=total=0
for arch in ['Characters','CharactersHD','Global']:
    rows=[(int(u,16),int(o),int(s)) for u,o,s in csv.reader(open(os.path.join(base,arch+'_index.csv')))]
    uids={u:s for u,o,s in rows}
    path=lambda u: os.path.join(base,arch,'_Unknown_','0x%08X.bin'%u)
    head={u:(open(path(u),'rb').read(4) if s>0 and os.path.exists(path(u)) else b'') for u,s in uids.items()}
    od=os.path.join(out,arch); os.makedirs(od,exist_ok=True)
    for u,s in uids.items():
        if s==0 or head[u]==TMP: continue
        os.link(path(u), os.path.join(od,'0x%08X.perm.bin'%u)); total+=1
        t=u^MASK
        if t in uids and uids[t]>0 and head[t]==TMP:
            os.link(path(t), os.path.join(od,'0x%08X.temp.bin'%u)); paired+=1
print('perm',total,'paires',paired)
