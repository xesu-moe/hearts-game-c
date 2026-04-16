FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc make libc6-dev libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .
RUN make server lobby

# --- Runtime ---
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libsqlite3-0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /build/hh-server /app/
COPY --from=build /build/hh-lobby /app/
COPY --from=build /build/assets/defs/ /app/assets/defs/

RUN mkdir -p /data
EXPOSE 7777 7778
CMD ["/bin/sh", "-c", "/app/hh-lobby 7778 /data/lobby.db & exec /app/hh-server 7777 127.0.0.1 7778 ${PUBLIC_ADDR:-127.0.0.1}"]