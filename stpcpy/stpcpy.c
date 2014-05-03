// I went to write this based on a random explanation, but when I went and compared to an implementation, it was almost exactly the same. Go figure.
// Just a disclaimer.
// WTFPL
char * stpcpy (char * destination, char * source)
{
    while ((* destination++ = * source++) != '\0');
    return destination - 1;
}
// Yeah, for some reason my toolkit doesn't have this, and one of the libraries I have to link depends on it being linked. Yay!
