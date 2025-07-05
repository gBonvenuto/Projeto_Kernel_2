savedcmd_dict_driver.mod := printf '%s\n'   dict_driver.o | awk '!x[$$0]++ { print("./"$$0) }' > dict_driver.mod
