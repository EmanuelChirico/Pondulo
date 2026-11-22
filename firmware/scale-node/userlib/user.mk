#Userlib directories
USERLIB = ./userlib

# List of all the Userlib files
USERSRC =  $(USERLIB)/src/hc595.c \
           $(USERLIB)/src/cl3641bh8.c \
           $(USERLIB)/src/hx711.c \
           $(USERLIB)/src/weight_dac.c
          
# Required include directories
USERINC =  $(USERLIB) \
           $(USERLIB)/include 

# Shared variables
ALLCSRC += $(USERSRC)
ALLINC  += $(USERINC)