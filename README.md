# compdb-vs

This is a tool for generating a compilation database (`compile_commands.json`) for a CMake based C/C++ project when Visual Studio is being used as the generator.

CMake will only output the compilation database when Ninja or Unix Makefiles are specified as the generator. The compilation database is what is needed by many LSP tools such as `clangd`. Therefore, it can be tricky to get working LSP on a project where you want/need to Visual Studio as the generator and you're using an editor like Vim where you need your own LSP setup. This tool solves that problem by generating a compilation database based on the generated Visual Studio compilation logs.

This tool is _unstable_ and _not well tested_. It seems to work fine for my own personal use (except for known issues, see below) but it could break at the mercy of Microsoft/LLVM (see below).

## Usage

Building and running this project has only been tested on Windows because, well, where else would you use it? First, build the project.

```bash
> mkdir build
> cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
> cmake --build build --config Release
```

Then, either install it with `cmake --install build` or add the build directory to your path.

Now, simply run the executable from the root of your project (or from within the same directory as your build folder).

`compdb-vs` will look for the build files and write the compilation database in a directory `build` relative to the current working directory. If your build folder is called something else, specify this with the `--build-dir/-b` flag.

```bash
C:/my-project> compdb-vs.exe --build-dir MyBuildFolder
```

`compdb-vs` also needs to know what configuration (Debug, Release etc) to look for because of the way that the Visual Studio build files are arranged. The default is Debug, but this can be changed with the `--config/-c` flag.

```bash
C:/my-project> compdb-vs.exe --build-dir ReleaseBuild --config Release
```

This unfortunately means that you will need to re-run `compdb-vs` every time you build with a different config (as well as obviously whenever the files/compiler settings of your project change), with that config specified. Adding the call to a build script you may have can streamline this.

By default, `compdb-vs` will also add entries for any header files you include in your source files. It does this by going through each item in the generated compilation database, parsing the file to see what files are included with an `#include` directive, then trying to append these included files to all of the include paths in that entry in the database, and for every one of these that exist it adds an entry with the same compile options. It does this until no additional entries are made. You can disable this behaviour with the `--skip-headers/-sh` flag.

## It Might Break™

I'm making a lot of educated assumptions for this to work. `compdb-vs` recursively looks for `CL.command.1.tlog` files in the build folder which contain the commands given to `cl.exe` to compile each file. It _seems_ like the name of the file is always the last part of the command, and they're always upper-case, so this is an assumption I make to match the source files in the generated compilation database entries.

The other thing is that from analyzing `clangd`'s source code (by the way, any other language server you'd use if you're doing your own LSP setup like `ccls` or `cquery` just use `libclang` to parse the compilation database, so I think I can just use `clangd` as a reference), it checks if the first argument in the command invokes `cl.exe`, and if it does then it knows to look for Visual Studio style arguments (`/Od`, `/WX` etc) and then ignores the first argument. So, for all of the commands in the `.tlog` files, I just insert "`cl.exe`" to the front and fix the casing.

If any of the above conventions change by the hands of Microsoft or LLVM, this will break.

## Known Issues
* It _seems_ like `clangd` will ignore an argument it doesn't recognise, but there might be some Visual Studio argument that will break it.
* When adding entries for header files, if two source files include the same header file but with _different_ compile options, only the first one that `compdb-vs` finds will be used.

## TODO
* Better testing.

