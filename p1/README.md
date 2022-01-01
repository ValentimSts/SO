# P1 (Tecnico file system)

## exc 1

- em vez do i_node apenas conter um campo para uma unica posicao do bloco de memoria, ira conter um vetor de 11 posições? (10 de memoria direta, 1 de memoria indireta)
- alterar funções para que funcionem com a nova implementação (write, read, ...)


# Update Log

## V

- Added update log to README.md file
- Added TODO's all over the code with commented code, to be reviewed
- Changed the i_node parameters to use pointers. Changed methods accordingly (need to review tfs_read)
- Fixed some stuff, need to fix write/read (curr block incrementation). Commented on the copy_to_external files to compile the program, as it wouldn't run otherwise (need tfs_copy_to_external_file)
- added pthread.h to state.c, implemented locks on state_init()

## J

- :3

## V/J

- Added the codes and stuff ("finished" 1.). Need to review "TODO" comments
- Finished 1.
- Finished 2.