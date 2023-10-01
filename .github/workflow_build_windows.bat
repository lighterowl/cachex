call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars32.bat"
cl /std:c++20 /O2 /W4 /MT /arch:IA32 /utf-8 /permissive- /EHsc cachex.cpp
