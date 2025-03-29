# TigerViewer

TigerViewer est un outil de visualisation interactif pour le projet Tiger, le compilateur Tiger développé en C++ à EPITA.

### Fonctionnalités :
#### Éditeur de code
Modifiez un buffer contenant du code source Tiger. L'AST généré est affiché dès que du code valide (selon les options activées) a été écrit.

#### Compilation Tiger avec options
L’exécutable **tc** lit le code depuis l'entrée standard et produit un AST au format DOT, qui est converti en SVG via Graphviz. Différentes options sont activables pour rajouter des étapes supplémentaires à la génération de l'AST final.

#### Visualisation de l'AST
Le SVG est affiché avec zoom (contrôlé par un slider vertical allant de 0.05 à 3.5), pan (déplacement par "grab") et un bouton Reset qui affiche le graphe en entier.

#### Fenêtre des logs
Affiche la sortie d'erreur (stderr) du compilateur Tiger.

#### Ouvrir un fichier
Le bouton « Open File » ouvre un explorateur système (TinyFileDialogs) filtrant les fichiers .tig et .tih. Le contenu est chargé dans l'éditeur de code.

### Installation
Cloner le projet et initialiser les submodules :
```
git clone https://github.com/ArthurGrd/TigerViewer
cd TigerViewer
git submodule update --init --recursive
```
Compiler avec CMake :
```
mkdir build
cd build
cmake ..
make
```
### Utilisation
Exécutez l'application :
```
cp <pathToTc> ./
./TigerViewer
```
Utilisez le panneau gauche pour éditer le code Tiger et le panneau droit pour visualiser l'AST. Les options et le chargement de fichiers se trouvent dans la barre de menu.

### Dépendances
```
CMake
GLFW, GLEW, Dear ImGui
librsvg, cairo, glib-2.0

Graphviz (commande dot)
Votre compilateur Tiger (tc)
```

### Contributeur
- Arthur GARRAUD (arthur.garraud@epita.fr)

### Licence
GNU GENERAL PUBLIC LICENSE
