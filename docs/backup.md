# Backing up and restoring your setlists

Your setlists, the songs in them, and the settings per song (your own title, transpose and volume) live inside Merik. A **backup** puts all of that in **one file** that you can keep safe, copy to another computer, or restore after a clean install.

## What is in the backup, and what is not

**In the backup:** every setlist with its name and order, every song in it (the path of the file), your own title, the transpose and the volume per song, and which setlist was selected.

**Not in the backup:** the MIDI files and `.merik` files themselves. Only their locations are saved, so after a restore they must be **in the same place as before**: for example on the Desktop, in `C:\midi` or in your Documents folder. Back up those folders separately, with your normal backup.

Settings such as the SoundFont, the audio and MIDI settings and the web key are not part of the backup.

## Make a backup

1. **Settings, Tools, Backup, Back up all setlists...**
2. Choose where to save it. The suggested name is **merik.backup**.

## Restore a backup

1. **Settings, Tools, Backup, Restore from a backup...**
2. Choose the `merik.backup` file.
3. Merik shows how many setlists and songs it contains and asks for confirmation. **Restoring replaces ALL your current setlists and songs**, and playback stops. Press **Replace** to continue, or **Cancel**.

After the restore, songs whose files cannot be found (for example because they are on a different computer or a different drive letter) are shown **in red** in the list. Merik does not search for them. Put the files back in their old place, or add them again from their new location.

## Tips

- Make a backup before a big change, and before updating or moving to another computer.
- When you move to another computer, copy your MIDI and `.merik` folders to the **same paths** (the same drive letter on Windows, the same folder names on a Mac). Then restore the backup, and everything is as you left it.
- The backup is a small text (XML) file. It is safe to keep several versions.

Back to the [help overview](index.md).
