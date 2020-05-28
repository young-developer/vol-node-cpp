#!/bin/bash

PROJECT_HOME=$(cd $(dirname "${0}")/../ && pwd)

docker run -d --name volition-node-dev      \
    --restart unless-stopped                \
    --network="fallguy"                     \
    -p "9091:9091"                          \
    -v ${PROJECT_HOME}:/var/lib/volition    \
    volition/node-dev                       \
    --permit-control true -p 9091 -i 1 -s true -k /var/lib/volition/.keys/root.priv.json -g /var/lib/volition/genesis --simple-recorder-folder /var/lib/volition/persist-chain
