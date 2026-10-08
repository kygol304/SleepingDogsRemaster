T=[]
for i in range(256):
    c=i<<24
    for _ in range(8): c=((c<<1)^0x04C11DB7) if c&0x80000000 else (c<<1)
    T.append(c&0xFFFFFFFF)
def h(s,upper=False,prev=0xFFFFFFFF):
    for ch in s.encode():
        if upper and 97<=ch<=122: ch-=32
        prev=((prev<<8)&0xFFFFFFFF)^T[((prev>>24)^ch)&0xFF]
    return prev
if __name__=='__main__':
    print(hex(h("PlayerOne_Havok")), hex(h("PlayerOne_Havok",True)))
