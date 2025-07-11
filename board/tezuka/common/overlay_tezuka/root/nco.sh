#!/bin/sh
freq=$1
curl -X PATCH -H "Content-Type: application/json" -d '{"frequency":'$freq'}' "http://localhost:8000/api/ddc/config"

