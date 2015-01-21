all:
	@echo "make srv..."
	gcc -o srv srv_arch.c
	@echo "make cli..."
	gcc -o cli cli.c
clean:
	rm -f cli srv
