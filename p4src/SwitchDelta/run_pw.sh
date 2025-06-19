# set -x
function loop_exe()
{
    CMDLINE=$1
    while true ; do
        sleep 1s
        ${CMDLINE}
        if [ $? == 0 ] ; then
            break;
        fi
    done
}

pkill bf_switchd 
pkill bf_switchd 

rm bf_drivers.log
rm bf_drivers.log*
rm zlog-cfg-cur

echo "Run"
env "PATH=$PATH" "LD_LIBRARY_PATH=$LD_LIBRARY_PATH" bf_switchd --conf-file ./conf_pw.conf --install-dir $SDE_INSTALL/ --background &

# sleep 30

# python recirculate.py

loop_exe "bfshell -f port_enable100Gbps_batch.txt"

while true ; do
    sleep 1s
    bfshell -f port_show.txt > port_data
    up_ports=`grep -c UP port_data`
    echo $up_ports
    if [ $up_ports == "5" ] ; then
        break
    fi
done

python py_pw.py
python py_mc_pw.py

exit
