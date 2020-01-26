# ESP32-targz


![](ESP32-targz.png)



This library is a wrapper for the following two great libraries:

  - uzlib https://github.com/pfalcon/uzlib
  - TinyUntar https://github.com/dsoprea/TinyUntar

Decompression support for .tar and .gz files

This is a work in progress



Extract content from `.gz` file
-------------------------------

```C

  // mount spiffs (or any other filesystem)
  SPIFFS.begin(true);

  // expand one file
  gzExpander(SPIFFS, "/index_html.gz", SPIFFS, "/index.html");

  // expand another file
  gzExpander(SPIFFS, "/tbz.gz", SPIFFS, "/tbz.jpg");


```


Expand contents from `.tar` file to `/tmp` folder
-------------------------------------------------

```C

  // mount spiffs (or any other filesystem)
  SPIFFS.begin(true);

  tarExpander(SPIFFS, "/tobozo.tar", SPIFFS, "/tmp");


```



Expand contents from `.tar.gz`  to `/tmp` folder
------------------------------------------------

```C

  // mount spiffs (or any other filesystem)
  SPIFFS.begin(true);

  tarGzExpander(SPIFFS, "/tbz.tar.gz", SPIFFS, "/tmp");

```


Flash the ESP with contents from `.gz` file
-------------------------------------------

```C
  // mount spiffs (or any other filesystem)
    SPIFFS.begin(true);

  gzUpdater(SPIFFS, "/menu_bin.gz");


```


Credits:
--------

  - [pfalcon](https://github.com/pfalcon/uzlib) (uzlib maintainer)
  - [dsoprea](https://github.com/dsoprea/TinyUntar) (TinyUntar maintainer)
  

