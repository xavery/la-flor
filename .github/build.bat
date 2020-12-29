call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvars32.bat"

mkdir build
cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
ninja

7z a -t7z -m0=lzma -mx=9 -mfb=64 -md=32m -ms=on -sse LaFlor.7z LaFlor.exe
powershell -Command "Get-FileHash LaFlor.7z | Format-List"
