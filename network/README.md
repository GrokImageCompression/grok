# Network Support for Testing

This file describes various applications that can be used to store and manage JPEG 2000 images
on the network. In the case of MinIO, there is also an emulate.sh script in this folder 
to run the MinIO docker image after it has been built.


# S3

S3 is Amazon's popular cloud storage system. 
After setting up an S3 acccount, it can be managed as follows:

```
pip install awscli
aws configure
```

# MinIO

MinIO is an open-source, high-performance, distributed object storage system designed for cloud-native applications, data lakes, and AI/ML workloads. It provides an S3-compatible API, allowing it to integrate seamlessly with applications and tools built for Amazon S3.


## Build/run docker

docker volume create minio-data
docker build -t minio-ubuntu .

### Run the Docker container

```
docker run -d --rm --cap-add=NET_ADMIN -v minio-data:/data  --name minio-container-latest -p 9000:9000 -p 9001:9001 minio-ubuntu

```

Admin web page is at localhost:9001. Credentials are  minioadmin/minioadmin

As the container runs as a daemon, it can be stopped by

```
docker ps
docker stop CONTAINER_ID
```

where CONTAINER_ID is parsed from `docker ps` output.


To open a shell into the container, run

```
docker exec -it minio-container /bin/sh
```


### Create Bucket

Go to `localhost:9001` on host and create `grok` bucket


### Minio Client

#### Install mc and set alias to docker minio

```
curl -O https://dl.min.io/client/mc/release/linux-amd64/mc
chmod +x mc
sudo mv mc /usr/local/bin/
mc alias set local https://localhost:9000 minioadmin minioadmin
```

#### Upload file

`mc cp /path/to/yourfile.txt local/grok/ --insecure`

and verify bucket contents with

`mc ls local/grok --insecure`


#### Set GDAL AWS Credentials

```
export AWS_ACCESS_KEY_ID="minioadmin"
export AWS_SECRET_ACCESS_KEY="minioadmin"
export AWS_S3_ENDPOINT="http://localhost:9000"
export AWS_VIRTUAL_HOSTING=False
```

### See logs

`docker logs minio-container`


# rclone

Rclone is an open-source command-line tool for syncing, transferring, and managing files across various cloud and local storage systems. Often described as "rsync for cloud storage," it supports a wide range of storage providers and protocols, including MinIO, Amazon S3, Google Cloud Storage, and more, while offering high performance and flexibility.

rclone can be used as a local cache for a cloud storage service such as MinIO or S3. Configuring rclone as such a cache is described below.


## Local install

sudo -v ; curl https://rclone.org/install.sh | sudo bash


## rclone configuration

### `rclone config`

Remote name: minio
Storage type: Amazon S3
S3 provider: MinIO
Endpoint URL: http://localhost:9000
Access Key: minioadmin
Secret Key: minioadmin
Region: (optional, us-east-1)


`rclone config show` to show existing configurations.


## rclone usage

### list bucket contents
`rclone lsd minio:grok`


## mount minio

mkdir ~/miniogrok ~/miniogrok_cache
nohup rclone mount minio:grok ~/miniogrok \
--vfs-cache-mode full --cache-dir ~/miniogrok_cache --rc --rc-addr=localhost:5572 --rc-no-auth  --log-level DEBUG &


## unmount

fusermount -u ~/miniogrok

## kill rclone

killall rclone


## commands

rclone cat --header "Range: bytes=0-1023" minio:grok/IMG_PHR1B_P_202406071720183_SEN_7038440101-1_R1C1.JP2 > range_data.bin


## clear OS file cache

sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"

## clear rclone disk cache

rm -rf ~/miniogrok/*


## Install traffic control tools

tc qdisc add dev eth0 root netem delay 100ms
tc qdisc add dev eth0 root tbf rate 100mbit burst 32kbit latency 400ms

tc qdisc del dev eth0 root


## stress test

python3 stress.py ~/miniogrok/ESP_011277_1825_RED.JP2 ~/temp ~/minigrok_cache


# iSCSI

iSCSI (Internet Small Computer Systems Interface) is a protocol that enables block-level storage access over IP networks. It allows a client (initiator) to access storage on a remote server (target) as if it were a local block device, such as a hard drive.

## Target

### dependencies

### Ubuntu
```
sudo apt install open-iscsi
sudo apt install targetcli-fb
sudo apt install tcmu-runner
sudo systemctl enable iscsid
sudo systemctl enable open-iscsi
sudo systemctl enable target
sudo systemctl enable tcmu-runner
```

### run targetcli
`sudo targetcli`

### create 50 Gigabyte backing store
`> /backstores/fileio create iscsi_storage ~/j2k_iscsi_backing_store 50G`

### create new target IQN
`> /iscsi create`

### assign backing store to Logical Unit Number (LUN)
`> /iscsi/<IQN>/tpg1/luns create /backstores/fileio/iscsi_storage`

### create ACL for initiator
`> /iscsi/<IQN>/tpg1/acls create iqn.2004-10.com.ubuntu:01:ac57bcf296a`

### save and exit
```
> saveconfig
> exit
```


## Initiator

### Fedora
sudo systemctl enable --now iscsid
sudo systemctl enable --now iscsi

## Ubuntu
sudo apt install open-iscsi

### find IQN
sudo cat /etc/iscsi/initiatorname.iscsi

### discover <TARGET_IQN>
`sudo iscsiadm -m discovery -t sendtargets -p HOSTNAME`

### login to target
```
sudo iscsiadm -m node -T <TARGET_IQN> -p HOSTNAME --login
```

### mount
```
sudo mount /dev/sda /mnt/HOSTNAME
```

### verify mount
`df -h`

### copy a file
cp ~/temp/IMG_PHR1B_P_202406071720183_SEN_7038440101-1_R1C1.JP2 /mnt/hercules

### On reboot

```
sudo iscsiadm -m node -T <TARGET_IQN> -p HOSTNAME --login
sudo mount /dev/sda /mnt/HOSTNAME
```

# more notes

sudo systemctl start iscsid
sudo systemctl enable iscsid

sudo cat /etc/iscsi/initiatorname.iscsi
sudo iscsiadm -m discovery -t sendtargets -p hercules

ssh $USER@hercules
sudo targetcli
/iscsi/<HERCULES_IQN>/tpg1/acls create <LOCAL_IQN>
exit

sudo iscsiadm -m node -T iqn.2003-01.org.linux-iscsi.hercules.x8664:sn.89193fa31122 -p hercules --login
sudo mount /dev/sda /mnt/hercules/

### get size of sda
sudo blockdev --getsz /dev/sda

## OpenCAS

[OpenCAS](https://open-cas.com/) is a block device cache that can be used to cache data from iSCSI device.

### Configuration

### find partition to act as cache
ls -l /dev/disk/by-id/

choose `nvme-eui.00080d020008d29b-part4` as cache and
`scsi-36001405706021f4418242459ea7d4c11` as core

### create cache
sudo casadm -S -d /dev/disk/by-id/nvme-eui.00080d020008d29b-part4 --force

# load existing cache device (nvme0n1p4)
sudo casadm -S -d /dev/disk/by-id/nvme-eui.00080d020008d29b-part4  --load

# connect core to this cache device (only need to do this once)
sudo casadm -A -d /dev/disk/by-id/scsi-36001405706021f4418242459ea7d4c11 -i 1

## query sequential cutoff settings
sudo casadm --get-param --name seq-cutoff --cache-id 1 --core-id 1 --output-format csv

## disable sequential cutoff (core must be connected to cache, only do this once)
sudo casadm --set-param --name seq-cutoff --cache-id 1 --core-id 1 --policy never


# mount
sudo mount /dev/cas1-1 /mnt/hercules/

# get cache stats
sudo casadm -P -i 1


# unload cache device when no longer needed
sudo casadm -T -i 1

# For persistent caches across reboot, edit conf file
sudo vi /etc/opencas/opencas.conf


# test command
/usr/bin/time -v ~/src/grok/build/bin/grk_decompress -i /mnt/hercules/IMG_PHR1B_P_202406071720183_SEN_7038440101-1_R1C1.JP2 -o $HOMEe/temp/tile.tif -t 100 -v