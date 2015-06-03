for file in `find valid -name "*.json"`
do
    name=`basename $file`
    ./ims-jsonc -s $file 2> /dev/null    
    if [ $? -ne 0 ]; then
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