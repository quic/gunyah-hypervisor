# Docker

A Docker container can be used to host the build tools and QEMU simulator. This is an alternative to installing them directly on a host Linux workstation.

## Installation

Install Docker in your machine following the instructions: https://docs.docker.com/engine/install/

## Build the Docker image from a Dockerfile

A Dockerfile may be found here: [Gunyah support scripts](https://github.com/quic/gunyah-support-scripts)

To build the Docker image, first go to the directory that contains the [Dockerfile](https://github.com/quic/gunyah-support-scripts/tree/develop/gunyah-qemu-aarch64/Dockerfile):
```bash
cd <path-to-dockerfile>
```

Build the Dockerfile and give it a name and optionally a tag using the following command:
```bash
docker build -f Dockerfile -t <name>:<tag> .
```

> Note, for building this image you may need to increase the available disk space for Docker.

Finally, run the generated Docker image with this command:
```bash
docker run -it <name>:<tag>
```
## Development

Once your Docker container is setup, proceed to building the hypervisor: [Build instructions](build.md)
