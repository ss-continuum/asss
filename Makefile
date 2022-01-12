docker:
	#docker run --rm -it -v $(pwd):/asss --workdir=/asss --platform linux/amd64 ubuntu:14.04
	rm -Rf bin/* build/*
	docker build -t asss:1.6.2 --platform linux/amd64 .
