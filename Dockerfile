FROM ubuntu:20.04
RUN apt-get update && apt-get upgrade -y
RUN apt-get install -y wget lsb-release wget software-properties-common curl gcc g++ cmake
WORKDIR /app
RUN wget https://apt.llvm.org/llvm.sh
RUN chmod +x llvm.sh
RUN ./llvm.sh 10
RUN rm llvm.sh
RUN bash -c "curl https://sh.rustup.rs -sSf | sh -s -- --default-toolchain nightly -y"
COPY . .
ENTRYPOINT ["/bin/bash"]
