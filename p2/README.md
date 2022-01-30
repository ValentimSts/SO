# Perguntas

 - Como lidamos com return values "de erro" no init e destroy (funcoes void)? Um printf chega? Devemos dar simplesmente return ou exit()?
 - Devemos utilizar um mutex ou rwlock? E uma cond variable para proteger a "queue", certo?
 - No mount, depois de escrevermos para a pipe do server e ler da pipe do cliente temos de as fechar? close()
 - Para enviar dados entre pipes, utilizamos apenas um char buffer[] ou ha alguma maneira de usar structs?
 - Cada uma das funcoes do server tera um buffer diferente?
 - 
