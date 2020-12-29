call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvars32.bat"

mkdir build
cd build
cmake -G "Visual Studio 16" -A Win32 -DCMAKE_GENERATOR_TOOLSET=v141_xp -DCMAKE_C_FLAGS="/arch:IA32" ..
msbuild -p:Configuration=Release ALL_BUILD.vcxproj

cd Release
7z a -t7z -m0=lzma -mx=9 -mfb=64 -md=32m -ms=on -sse LaFlor.7z LaFlor.exe
certutil -hashfile LaFlor.7z SHA256
