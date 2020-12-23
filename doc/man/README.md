Man pages sources are stored in markdown format, and converted to
`troff` via [pandoc](https://eddieantonio.ca/blog/2015/12/18/authoring-manpages-in-markdown-with-pandoc/) 

Example:

`pandoc --standalone --to man grk_dump.1.md -o ../../man1/grk_dump.1`
