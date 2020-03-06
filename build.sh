set -e
cmake -GNinja --config RelWithDebInfo --build .
ninja $@
