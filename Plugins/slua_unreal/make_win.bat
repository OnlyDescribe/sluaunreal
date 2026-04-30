mkdir build_win32 & pushd build_win32
cmake -G "Visual Studio 17 2022" -A Win32 ..
popd
cmake --build build_win32 --config RelWithDebInfo
md Library\Win32
copy /Y build_win32\RelWithDebInfo\lua.lib Library\Win32\lua.lib
copy /Y build_win32\RelWithDebInfo\lua.pdb Library\Win32\lua.pdb
copy /Y build_win32\RelWithDebInfo\lua.exe Library\Win32\lua.exe
copy /Y build_win32\RelWithDebInfo\lua_cli.pdb Library\Win32\lua_cli.pdb

mkdir build_win64 & pushd build_win64
cmake -G "Visual Studio 17 2022" -A x64 ..
popd
cmake --build build_win64 --config RelWithDebInfo
md Library\Win64
copy /Y build_win64\RelWithDebInfo\lua.lib Library\Win64\lua.lib
copy /Y build_win64\RelWithDebInfo\lua.pdb Library\Win64\lua.pdb
copy /Y build_win64\RelWithDebInfo\lua.exe Library\Win64\lua.exe
copy /Y build_win64\RelWithDebInfo\lua_cli.pdb Library\Win64\lua_cli.pdb
