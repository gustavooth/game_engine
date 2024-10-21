echo "Compiling game_engine..."

buildf="build"
if [ ! -d "$buildf" ]; then
    mkdir "$buildf"
fi

cd $buildf
cmake -G Ninja ..
ninja
