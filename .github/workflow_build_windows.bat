call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars32.bat"
cl /std:c++20 /O2 /W4 /MT /arch:IA32 /utf-8 /permissive- /EHsc cachex.cpp
7z a -t7z -m0=lzma -mx=9 -mfb=64 -md=32m -ms=on -sse cachex.7z cachex.exe
certutil -hashfile cachex.7z SHA256
