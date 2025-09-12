FROM ubuntu:22.04

# install compiler & tools
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# set working dir
WORKDIR /app

# copy source code
COPY . .

# build the server
RUN gcc -O2 -Wall -o gpu_metrics_server gpu_metrics.c

# expose a port for your server
EXPOSE 7654

# run the C server
CMD ["./gpu_metrics_server"]
