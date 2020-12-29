# La Flor : A Mouse Mover

![A flower](https://raw.githubusercontent.com/paomedia/small-n-flat/master/png/96/flower.png)

Haven't you ever wanted to move your mouse by a given pixel distance, at a given
interval? Also, haven't you ever wanted such an application to also have :

 * a pretty icon,
 * very small size,
 * totally static linking,
 * compatibility all the way back to Windows XP?

If so, this program is meant for you.

# But seriously...

It is true that I wanted to have an application which would automagically move
my mouse cursor, simulating actual user input. I made something that worked with
Qt, but was disappointed by the fact that a static build was 9MB in size due to
including all the Qt stuff that the application didn't really need, as it pretty
much can be written just using raw WinAPI directly.

So, I set out to write just that, but with the following goals :
 * use only plain C,
 * use only raw WinAPI to avoid any unneeded libraries,
 * don't use any global variables (which are present in a lot of SDK examples),
 * don't use any MSVC- or Windows-specific build tools except where strictly necessary.

I am not an experienced WinAPI developer, so this also served as an exercise for
me. Therefore, some of the code comments are probably a bit too verbose, but
perhaps they can serve somebody else when writing raw WinAPI applications.

# And the name?

I just really liked the icon, courtesy of the
[Small-n-flat](http://paomedia.github.io/small-n-flat/) icon set.
