# goszifox
## goszifox adaptat al oszifox distribuit a Espanya

Oxzifox és un oscil·loscopi de butxaca que es connecta a l'ordinador per RS-232.

Al 2012 vaig trobar alguns programes que permeten el seu ús des de GNU/Linux, com XProbeScope (http://www.linuxtoys.org/pscope/pscope.html) o goszifox
(http://excamera.com/articles/22/goszifox.html).

Tots dos ofereixen el codi font en C, i al primer enllaç trobareu la descripció del protocol sèrie que utilitza.

Jo vaig comprar el meu fa una pila d'anys, però no funcionava amb aquest programes. Finalment vaig descubrir que a la versió que jo tenia (comercialitzada a Espanya per High-Tech-Diesel) havien canviat el codi de sincronisme 0x7F per 0x5F.

Ja posats a modificar, vaig agafar el codi del goszifox (molt més agradable) i vaig afegir una mica de codi per moure horitzontal i verticalment el traç,
utilitzant el teclat numèric (4 esquerra, 6 dret, 8 pujar, 2 baixar, 5 centrar) i així poder fer mesures com fem als oscil·loscopis analògics. 

El programa original només responia a les tecles espai (pausa) i a les tecles q i ESC (sortir).

Aquí us deixo la meva versió. També està compilada per la meva Debian Etch amd64, però la podeu compilar amb el gcc per qualsevol altra distribució i
arquitectura utilitzant el script compila afegit. Per defecte utilitza el port */dev/ttyS0*. Per fer servir el port */dev/ttyUSB0* (us caldrà un adaptador USB-RS232) feu

> goszifox -p /dev/ttyUSB0

## Actualització agost 2020

Comprovo que la mateixa versió compilada funciona amb Ubuntu 20.04. Només cal instal·lar la llibreria GLUT:

> sudo apt install freeglut3
