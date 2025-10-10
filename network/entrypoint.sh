#!/bin/bash

# Ensure /data and /root/.minio/certs are preserved
if [ ! -d /data ]; then
    echo "Creating /data directory"
    mkdir -p /data
fi
if [ ! -d /root/.minio/certs ]; then
    echo "Creating /root/.minio/certs directory"
    mkdir -p /root/.minio/certs
fi

# Get the container's IP address
CONTAINER_IP=$(ip addr show eth0 | grep -oP '(?<=inet\s)\d+(\.\d+){3}' | head -1)
if [ -z "$CONTAINER_IP" ]; then
    echo "Failed to determine container IP address"
    exit 1
fi

# Configure /etc/hosts for virtual host-style access
if [ -z "$MINIO_DOMAIN" ]; then
    echo "MINIO_DOMAIN environment variable not set, virtual host-style access disabled"
else
    echo "Configuring /etc/hosts for virtual host-style access with domain $MINIO_DOMAIN"
    echo "$CONTAINER_IP $MINIO_DOMAIN" >> /etc/hosts

    if [ ! -z "$MINIO_BUCKETS" ]; then
        IFS=',' read -ra BUCKET_ARRAY <<< "$MINIO_BUCKETS"
        for bucket in "${BUCKET_ARRAY[@]}"; do
            echo "$CONTAINER_IP ${bucket}.$MINIO_DOMAIN" >> /etc/hosts
            echo "Added ${bucket}.$MINIO_DOMAIN to /etc/hosts"
        done
    fi
fi

# Execute the MinIO server with the provided command
exec "$@"