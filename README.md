# FCC – Falcon Cockpit Camera

Plugin X-Plane 12 pour sauvegarder des positions de caméra (tête du pilote en vue cockpit 3D) et les rappeler avec un raccourci clavier.

## Utilisation

1. Menu **Plugins → FCC → Open window** (la fenêtre se déplace en glissant la barre de titre).
2. Place ta vue où tu veux dans le cockpit, puis clique **SAVE** sur la ligne voulue.
3. Clique sur le bouton **Keybind** : il affiche « Waiting for keybind... », appuie sur la touche (ou une combinaison Ctrl/Shift/Alt + touche). `Échap` annule, `Retour arrière` efface.
4. Clique sur le **nom** pour le modifier (Entrée pour valider).
5. Appuie sur le keybind pour revenir à cette position. Si tu n'es pas en vue cockpit 3D, le plugin y bascule d'abord.

Sont sauvegardés : position X/Y/Z de la tête, cap, tangage et champ de vision (FOV).

Chaque position expose aussi deux commandes X-Plane, bindables dans *Réglages → Clavier/Joystick* :
`FCC/recall_1…8` et `FCC/save_1…8`.

Les données sont écrites dans `Falcon8X/plugins/FCC/cameras.cfg`.

## Installation

```
Falcon8X/plugins/FCC/64/win.xpl   (ou lin.xpl / mac.xpl)
```

## Compilation

1. Télécharge le SDK X-Plane : https://developer.x-plane.com/sdk/plugin-sdk-downloads/
2. ```
   cmake -B build -DXPLM_SDK=<chemin/du/SDK>
   cmake --build build --config Release
   ```
3. Le `.xpl` est généré dans `dist/FCC/64/`. Copie le dossier `FCC` dans `Falcon8X/plugins/`.
