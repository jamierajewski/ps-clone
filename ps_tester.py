#!/usr/bin/python

import socket
import sys
import threading
from time import sleep

lock = threading.Lock()
username = "rajewskij"
count = 0

def worker(server_addr, reqs):
    for i in range(int(reqs)):
        client_sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        
        try:
            client_sock.connect(server_addr)
            client_sock.setblocking(0)

        except:
            print("\nFAIL ON CONNECT - CLIENT\n")
            sys.exit(1)

        client_sock.send(bytes(username + "\n", "utf-8"))
        for i in range(20):
            try:
                final = ''
                recvstr = 'start'
                while recvstr is not '':
                    chunk = client_sock.recv(16)
                    recvstr = chunk.decode('utf-8')
                    final += recvstr
                print(final)
            except:
                sleep(0.1)
        lock.acquire()
        global count
        count += 1
        lock.release()

        client_sock.close()


def main(server_addr, cons, reqs):
    total = int(cons)*int(reqs)
    thread_list = []
    for i in range(int(cons)):
        try:
            thread = threading.Thread(target=worker, args=(server_addr, reqs))
            thread_list.append(thread)
            thread.start()

        except:
            print("\nERROR CREATING THREAD\n")
            sys.exit(1)
            
    while(threading.active_count() > 1):
        pass
        #print("Thread still active\n")
    for thr in thread_list:
        thr.join()
    print("\nTotal: "+str(total)+" Actual: "+str(count))

if (len(sys.argv) != 4):
    print("\nUsage: python3 ps_tester.py socket connections requests")
    sys.exit(1)

else:
    main(sys.argv[1], sys.argv[2], sys.argv[3])
