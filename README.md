# memory_fragment


add:
    echo '+id=<id> pagenum=<num> blocknum=<num>' > /proc/fragmem 
del:
    echo '+ id=<id>' > /proc/fragmem
