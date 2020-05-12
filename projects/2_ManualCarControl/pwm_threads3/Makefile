TARGET=pwm_hw_threads

SOURCES=import_registers.c \
        enable_pwm_clock.c \
        pwm_hw_threads.c

OBJECTS=$(patsubst %.c,%.o,$(SOURCES))

all: $(OBJECTS)
	gcc $(OBJECTS) -lpthread -o $(TARGET)

clean:
	rm -f $(OBJECTS) $(TARGET)

%.o:%.c
	gcc -c $< -o $@
