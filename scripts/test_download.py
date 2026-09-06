"""Full local UDP tracker + TCP peer download of synthetic data, no public swarm."""
import hashlib
import pathlib
import socket
import struct
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor

def encode(x):
    if isinstance(x, int): return b'i'+str(x).encode()+b'e'
    if isinstance(x, bytes): return str(len(x)).encode()+b':'+x
    if isinstance(x, dict): return b'd'+b''.join(encode(k)+encode(v) for k,v in sorted(x.items()))+b'e'
    return b'l'+b''.join(map(encode,x))+b'e'

def exact(s, n):
    result=b''
    while len(result)<n:
        chunk=s.recv(n-len(result))
        if not chunk: raise EOFError('peer closed')
        result+=chunk
    return result

def message(s, kind, payload=b''):
    s.sendall(struct.pack('!IB',len(payload)+1,kind)+payload)

data=bytes((i*17+3)%251 for i in range(70001))
piece_size=32768
for mode in ('good','multi','corrupt','disconnect','choke','choke_resume','webseed','disk'):
    with tempfile.TemporaryDirectory() as temp, socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as udp, socket.socket() as tcp:
        root=pathlib.Path(temp); udp.bind(('127.0.0.1',0)); udp.settimeout(10)
        tcp.bind(('127.0.0.1',0)); tcp.listen(); tcp.settimeout(10)
        info={b'name':b'data.bin',b'length':len(data),b'piece length':piece_size,
              b'pieces':b''.join(hashlib.sha1(data[i:i+piece_size]).digest() for i in range(0,len(data),piece_size))}
        if mode=='multi':
            del info[b'length']
            info[b'files']=[{b'length':30333,b'path':[b'first.bin']},
                            {b'length':len(data)-30333,b'path':[b'sub',b'second.bin']}]
        torrent={b'announce':f'udp://127.0.0.1:{udp.getsockname()[1]}/announce'.encode(), b'info':info}
        if mode=='webseed':
            torrent={b'url-list':f'http://127.0.0.1:{tcp.getsockname()[1]}/data.bin'.encode(), b'info':info}
        max_webseed_range=[0]
        source=root/'sample.torrent'; source.write_bytes(encode(torrent))
        def tracker():
            req,addr=udp.recvfrom(2048); tid=req[12:16]
            udp.sendto(struct.pack('!I',0)+tid+struct.pack('!Q',123),addr)
            req,addr=udp.recvfrom(2048); tid=req[12:16]
            udp.sendto(struct.pack('!I',1)+tid+struct.pack('!III',120,0,1)+socket.inet_aton('127.0.0.1')+struct.pack('!H',tcp.getsockname()[1]),addr)
        def peer():
            with tcp.accept()[0] as conn:
                conn.settimeout(10)
                hs=exact(conn,68); assert hs[20:28]==b'\0'*8
                assert hs[28:48]==hashlib.sha1(encode(info)).digest()
                conn.sendall(hs[:48]+b'-TEST00-123456789012')
                assert exact(conn,5)==b'\0\0\0\1\2'
                message(conn,5,b'\x60')  # Only pieces 1 and 2, then advertise 0 via HAVE.
                message(conn,1)
                received=0; requested=[]
                while received<len(data):
                    n=struct.unpack('!I',exact(conn,4))[0]; req=exact(conn,n)
                    assert req[0]==6 and n==13
                    index,begin,length=struct.unpack('!III',req[1:]); requested.append((index,begin,length))
                    assert 0<length<=16384 and begin%16384==0
                    if len(requested)==1: assert index==1
                    if mode=='disconnect': return
                    block=data[index*piece_size+begin:index*piece_size+begin+length]; assert len(block)==length
                    if mode=='corrupt': block=bytes([block[0]^1])+block[1:]
                    packet=struct.pack('!IBII',length+9,7,index,begin)+block
                    conn.sendall(packet[:2]); conn.sendall(packet[2:])
                    received+=length
                    if mode=='choke':
                        message(conn,0)
                        time.sleep(10)
                        return
                    if mode=='choke_resume' and len(requested)==1:
                        message(conn,0)
                        time.sleep(0.05)
                        message(conn,1)
                    if mode in ('corrupt','disk') and begin+length==piece_size: return
                    if index==2: message(conn,4,struct.pack('!I',0))
                assert len(requested)==5 and requested[-1]==(0,16384,16384)
        def webseed():
            served=[0]
            while served[0] < len(data):
                with tcp.accept()[0] as conn:
                    conn.settimeout(10)
                    request=b''
                    while b'\r\n\r\n' not in request:
                        chunk=conn.recv(4096)
                        if not chunk: raise EOFError('web seed closed')
                        request+=chunk
                    marker=b'Range: bytes='
                    start=request.index(marker)+len(marker)
                    end=request.index(b'\r\n',start)
                    first,last=map(int,request[start:end].split(b'-'))
                    body=data[first:last+1]
                    max_webseed_range[0]=max(max_webseed_range[0],len(body))
                    served[0]+=len(body)
                    time.sleep(0.1)
                    header=(b'HTTP/1.0 206 Partial Content\r\nContent-Length: '+
                            str(len(body)).encode()+b'\r\n\r\n')
                    conn.sendall(header+body)
        with ThreadPoolExecutor(max_workers=2) as pool:
            tasks=[pool.submit(webseed)] if mode=='webseed' else [pool.submit(tracker),pool.submit(peer)]
            subprocess.run([sys.argv[1],str(source),temp,'good' if mode=='multi' else mode],check=True,timeout=15)
            for task in tasks: task.result()
        if mode=='good': assert (root/'data.bin').read_bytes()==data
        if mode=='multi':
            assert (root/'first.bin').read_bytes()==data[:30333]
            assert (root/'sub'/'second.bin').read_bytes()==data[30333:]
        if mode=='webseed':
            assert (root/'data.bin').read_bytes()==data
            assert max_webseed_range[0] > piece_size
        print(f'Download {mode}: passed',flush=True)
