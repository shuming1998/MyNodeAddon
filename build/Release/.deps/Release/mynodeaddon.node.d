cmd_Release/mynodeaddon.node := c++ -bundle -L/opt/homebrew/Cellar/llvm@14/14.0.6/lib -Wl,-search_paths_first -Wl,-headerpad_max_install_names -lLLVM-14 -undefined dynamic_lookup -Wl,-search_paths_first -mmacosx-version-min=11.0 -arch arm64 -L./Release -stdlib=libc++ -L/opt/homebrew/Cellar/llvm@14/14.0.6/lib -o Release/mynodeaddon.node Release/obj.target/mynodeaddon/src/myaddon.o 
