# MinIO Dockerfile

# Use Ubuntu as the base image
FROM ubuntu:latest

# Install dependencies
RUN apt-get update && apt-get install -y \
    wget \
    ca-certificates \
    curl \
    iproute2 \
    openssl \
    && rm -rf /var/lib/apt/lists/*

# Download MinIO server binary
RUN wget https://dl.min.io/server/minio/release/linux-amd64/minio \
    && chmod +x minio \
    && mv minio /usr/local/bin/

# Download MinIO client (mc) binary
RUN wget https://dl.min.io/client/mc/release/linux-amd64/mc \
    && chmod +x mc \
    && mv mc /usr/local/bin/

# Create directories for data and certificates
RUN mkdir -p /data /root/.minio/certs

# Generate self-signed certificates (for testing)
RUN openssl req -x509 -newkey rsa:4096 -keyout /root/.minio/certs/private.key \
    -out /root/.minio/certs/public.crt -days 365 -nodes \
    -subj "/CN=*.minio.example.com" -addext "subjectAltName=DNS:minio.example.com,DNS:*.minio.example.com" \
    && chmod 600 /root/.minio/certs/private.key /root/.minio/certs/public.crt

# Copy custom entrypoint script
COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

# Expose MinIO ports (9000 for S3 API, 9001 for Console)
EXPOSE 9000 9001

# Set the working directory
WORKDIR /root

# Set environment variables for credentials and domain
ENV MINIO_ROOT_USER=minioadmin \
    MINIO_ROOT_PASSWORD=minioadmin \
    MINIO_DOMAIN=minio.example.com

# Use custom entrypoint to configure virtual host support
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]

# Set the command to run MinIO server with TLS
CMD ["minio", "server", "/data", "--address", ":9000", "--console-address", ":9001"]