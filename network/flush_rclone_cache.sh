killall rclone
killall rclone
fusermount -u ~/miniogrok
rm -rf ~/miniogrok_cache
sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
nohup rclone mount minio:grok ~/miniogrok --vfs-cache-mode full --cache-dir ~/miniogrok_cache \
  --rc --rc-addr=localhost:5572 &
