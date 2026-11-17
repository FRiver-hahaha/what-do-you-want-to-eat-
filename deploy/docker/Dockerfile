# syntax=docker/dockerfile:1
# Build stage: compile Muduo + eat-server. Runtime keeps only the binary and
# the client libraries it actually needs.
FROM ubuntu:24.04 AS build

SHELL ["/bin/bash", "-c"]

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake git ca-certificates libboost-dev \
        libssl-dev libmysqlclient-dev \
    && rm -rf /var/lib/apt/lists/*

# Muduo is compiled from source because Ubuntu has no reliable package.
RUN git clone --depth 1 https://github.com/chenshuo/muduo.git /opt/muduo \
    && cmake -S /opt/muduo -B /opt/muduo/build/release-cpp11 \
        -DCMAKE_BUILD_TYPE=Release -DMUDUO_BUILD_EXAMPLES=OFF \
    && cmake --build /opt/muduo/build/release-cpp11 -j"$(nproc)"

WORKDIR /src
COPY . /src
RUN cmake -S /src -B /src/build -DCMAKE_BUILD_TYPE=Release \
        -DEAT_MUDUO_ROOT=/opt/muduo \
    && cmake --build /src/build -j"$(nproc)" --target eat-server

# Generate the one-shot migration bundle while the sources are still present.
RUN mkdir -p /opt/eat/initdb && { \
        echo "CREATE DATABASE IF NOT EXISTS eat CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"; \
        echo "USE eat;"; \
        sed 's/chatroom/eat/g' /src/migrations/base/000_init.sql | tr -d '\r'; echo; \
        echo "SET @seed_pwd_hash = SHA2('disabled-login', 256);"; \
        tr -d '\r' < /src/migrations/base/001_p8_business.sql | sed 's/^--\([^ ]\)/-- \1/'; echo; \
        tr -d '\r' < /src/migrations/base/foods_seed.sql | sed 's/^--\([^ ]\)/-- \1/'; echo; \
        for f in /src/migrations/add/00{2,3,4,5,6}_*.sql; do \
            case "$f" in *_rollback.sql) continue;; esac; \
            tr -d '\r' < "$f" | sed 's/^--\([^ ]\)/-- \1/'; echo; \
        done; \
    } > /opt/eat/initdb/00_all.sql

FROM ubuntu:24.04

SHELL ["/bin/bash", "-c"]

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        libssl3 libstdc++6 libmysqlclient-dev default-mysql-client ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /opt/eat/bin /opt/eat/etc /opt/eat/initdb
COPY --from=build /src/build/eat-server /opt/eat/bin/eat-server
COPY --from=build /opt/eat/initdb/00_all.sql /opt/eat/initdb/00_all.sql
COPY deploy/docker/entrypoint.sh /opt/eat/bin/entrypoint.sh
RUN chmod +x /opt/eat/bin/entrypoint.sh

EXPOSE 9443
ENTRYPOINT ["/opt/eat/bin/entrypoint.sh"]
