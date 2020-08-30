Man pages sources are stored in markdown format, and converted to
`troff` via [pandoc](https://eddieantonio.ca/blog/2015/12/18/authoring-manpages-in-markdown-with-pandoc/) 

`pandoc --standalone --to man foo.1.md -o foo.1`
