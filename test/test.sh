for file in `find valid -name "*.json"`
do
    name=`basename $file`
    dir=`dirname $file`

    hash1=`./ims-jsonc --compact $file 2> /dev/null | md5`
    hash2=`md5 -q valid-out/$name`

    if [ $? -ne 0 ]; then
        echo -e "[\033[1;31mFAILURE\033[0m] $name"
    elif [ "$hash1" != "$hash2" ]; then
        echo -e "[\033[1;31mFAILURE\033[0m] $name"
    else
       echo -e "[\033[1;32mSUCCESS\033[0m] $name"
    fi
done

for file in `find invalid -name "*.json"`
do
    name=`basename $file`
    ./ims-jsonc -s $file 2> /dev/null
    if [ $? -eq 0 ]; then
        echo -e "[\033[1;31mFAILURE\033[0m] $name"
    else
        echo -e "[\033[1;32mSUCCESS\033[0m] $name"
    fi
done
