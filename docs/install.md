# Install Stremio Origin on an LG TV

Two ways: the Homebrew Channel, or the `.ipk` file from a computer.

## With the Homebrew Channel
The Homebrew Channel is installed on TVs set up with the [webOS Homebrew](https://www.webosbrew.org/) tools.

1. **Open the Homebrew Channel** on the TV, open its settings and choose **Repositories**.
2. **Add** `https://slvjtr1.github.io/webos/apps.json`.
3. **Find Stremio Origin** in the list and **install it**, then open Stremio Origin and sign in with your Stremio account.

## With a computer (Developer Mode)
**You need:** a computer on the same network as the TV, and the latest `tv.stremioorigin.app_<version>_arm.ipk` from [Releases](https://github.com/slvjtr1/webos/releases).

1. **On the TV, open the LG Content Store**, search for **Developer Mode**, and install it.
2. **Open Developer Mode** and sign in with an LG developer account. It is free: you can make one on the [webOS TV developer site](https://webostv.developer.lge.com/).
3. **Turn Dev Mode Status on.** The TV restarts. Open Developer Mode again and **turn Key Server on**. It now shows the TV's **IP address** and a **passphrase**. Keep this screen open.
4. **On the computer, install [Device Manager for webOS](https://github.com/webosbrew/dev-manager-desktop/releases)** and open it.
5. **Add the TV:** choose **Add Device**, type the TV's IP address and the passphrase.
6. **Install the file:** choose the `.ipk` you downloaded and install it.
7. **On the TV, open Stremio Origin** and sign in with your Stremio account.

## Good to know
- Developer Mode runs out after a while, and the apps installed this way are removed when it does. Open the Developer Mode app and press **Extend** to keep it on.
- **Updating:** update in the app, or from the Homebrew Channel.
