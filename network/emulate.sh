#!/bin/bash

# Set the delay as a variable (default to 3ms, override with argument if provided)
DELAY_MS="${1:-3}"  # Use first argument or default to 3

# Load sch_netem on host (run once per boot or make permanent)
sudo modprobe sch_netem 2>/dev/null || echo "sch_netem already loaded"

# Container configuration
CONTAINER_NAME="minio-container"
IMAGE_NAME="minio-alpine"
MINIO_DOMAIN="minio.example.com"
MINIO_BUCKETS="grok"  # Explicitly configure the 'grok' bucket
MINIO_ROOT_USER="minioadmin"
MINIO_ROOT_PASSWORD="minioadmin"

# Stop and remove existing container if running
docker stop "$CONTAINER_NAME" 2>/dev/null || true
docker rm "$CONTAINER_NAME" 2>/dev/null || true

# Run MinIO container with virtual host support (removed --rm for debugging)
docker run -d \
  --cap-add=NET_ADMIN \
  -v minio-data:/data \
  -v minio-certs:/root/.minio/certs \
  --name "$CONTAINER_NAME" \
  -p 9000:9000 \
  -p 9001:9001 \
  -e "MINIO_ROOT_USER=$MINIO_ROOT_USER" \
  -e "MINIO_ROOT_PASSWORD=$MINIO_ROOT_PASSWORD" \
  -e "MINIO_DOMAIN=$MINIO_DOMAIN" \
  -e "MINIO_BUCKETS=$MINIO_BUCKETS" \
  "$IMAGE_NAME"

# Wait for container to start and be inspectable (loop up to 10s)
for i in $(seq 1 10); do
  if docker inspect "$CONTAINER_NAME" &> /dev/null; then
    break
  fi
  sleep 1
done

# Get container PID
PID=$(docker inspect "$CONTAINER_NAME" | grep '"Pid":' | head -n 1 | awk '{print $2}' | tr -d ',')

if [ -z "$PID" ] || [ "$PID" -eq 0 ]; then
    echo "Error: Couldn't get PID for $CONTAINER_NAME (check 'docker logs $CONTAINER_NAME' for startup issues)"
    exit 1
fi

echo "Container PID: $PID"

# Enter network namespace and apply netem delay with the variable (use /bin/sh for Alpine compat)
sudo nsenter -t "$PID" -n /bin/sh -c "\
    tc qdisc replace dev eth0 root netem delay ${DELAY_MS}ms && \
    tc qdisc show dev eth0\
"

echo "Applied ${DELAY_MS}ms delay to $CONTAINER_NAME's eth0"
echo "MinIO running with virtual host support at $MINIO_DOMAIN"
echo "Bucket configured for virtual host access: $MINIO_BUCKETS"