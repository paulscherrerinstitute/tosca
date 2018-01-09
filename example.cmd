toscaRegDevConfigure user USER 0x1000
toscaRegDevConfigure csr TCSR
toscaSmonDevConfigure smon
toscaRegDevConfigure shm SHM1:1M 1M DL
dbLoadRecords example.template "P=$(IOC)"
