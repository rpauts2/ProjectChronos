@echo off
sc create chronos_drv binPath="C:\Users\Administrator\Desktop\CS2\ProjectChronos\build\chronos_drv.sys" type=kernel
sc start chronos_drv
