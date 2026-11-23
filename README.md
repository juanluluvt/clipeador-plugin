# Clipeador

Plugin de OBS Studio (Windows) que añade un panel **Clipeador** en el menú Docks:

- **30 s / 60 s:** crea un clip de Twitch con esa duración (el máximo que permite Twitch es 60 s).
- **90 s (local):** guarda el búfer de repetición de OBS en tu disco.
- **Marcador:** deja un marcador en el VOD, con descripción opcional.

## Compilar (lo hace GitHub solo)

1. Sube todo el contenido de esta carpeta a un repositorio de GitHub (incluida la carpeta `.github`).
2. Ve a la pestaña **Actions**. La compilación arranca sola; si no, pulsa **Compilar Clipeador > Run workflow**.
3. Cuando termine (unos 5-10 minutos), entra en la ejecución y descarga **Clipeador-plugin-windows** en la sección *Artifacts*.

## Instalar

1. Cierra OBS.
2. Extrae el zip descargado en `C:\ProgramData\obs-studio\plugins`. Debe quedar `C:\ProgramData\obs-studio\plugins\clipeador\bin\64bit\clipeador.dll`.
3. Abre OBS. En el menú **Docks** aparece **Clipeador**. En OBS 33 el Plugin Manager lo marca como "Legacy"; funciona igualmente.

## Conectar con Twitch

1. En https://dev.twitch.tv/console/apps registra una aplicación: Category *Application Integration*, Client Type **Public**. La Redirect URL puede ser `http://localhost`.
2. Copia el **Client ID** (30 caracteres) en el panel y pulsa **Conectar con Twitch**.
3. Se abre Twitch en tu navegador: pulsa **Autorizar**. El panel se conecta solo.

## Botón de 90 s

En OBS: Ajustes > Salida > Búfer de repetición, duración 90 s, y pulsa *Iniciar búfer de repetición*.
