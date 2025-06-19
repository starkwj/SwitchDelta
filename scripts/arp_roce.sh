server=("10.0.2.116" "10.0.2.116" "10.0.2.115" "10.0.2.117" "10.0.2.118" "10.0.2.119")
name=("ens2" "ens6" "ens2" "ens6" "ens6" "ens6np0")
nic_ip=("192.168.2.116" "192.168.3.116" "192.168.2.115" "192.168.2.117" "192.168.2.118" "192.168.2.119")
mac=("ec:0d:9a:ae:14:80" "0c:42:a1:c9:84:74" "ec:0d:9a:c0:41:c0" "0c:42:a1:c9:84:6c" "0c:42:a1:c9:41:a4" "0c:42:a1:c9:84:a4")

# server=("10.0.2.116" "10.0.2.116" "10.0.2.117")
# name=("ens2" "ens6"  "ens2")
# nic_ip=("192.168.2.116" "192.168.3.116" "192.168.2.117")
# mac=("ec:0d:9a:ae:14:80" "0c:42:a1:c9:84:74" "ec:0d:9a:c0:41:cc")

set -x

install(){
    ssh $1 "ifconfig $2 $3/22 up" 
    ssh $1 "ifconfig $2 mtu 4200"     
    for i in ${!nic_ip[@]};
    do
        ssh $1 "arp -s ${nic_ip[$i]} ${mac[$i]} -i $2"
    done
}

for ip in ${!server[@]}
do
    install ${server[$ip]} ${name[$ip]} ${nic_ip[$ip]}
done