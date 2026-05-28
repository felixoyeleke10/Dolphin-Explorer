# Website Code Structure

This website uses a modular React structure so content, layout, and styling can evolve without collapsing back into one large file.

## Directory Structure

```text
src/
|-- components/            Reusable React components
|   |-- Header.jsx         Top navigation and search
|   |-- Sidebar.jsx        Section navigation drawer
|   |-- FeaturedSection.jsx
|   |-- NewsSection.jsx
|   |-- AboutSection.jsx
|   |-- ContactSection.jsx
|   `-- ArticleDialog.jsx
|
|-- data/                  Site content and navigation data
|   |-- navigation.js
|   `-- articles.js
|
|-- styles/                Modular CSS files
|   |-- index.css
|   |-- base.css
|   |-- header.css
|   |-- sidebar.css
|   |-- featured.css
|   |-- news.css
|   |-- sections.css
|   |-- footer.css
|   `-- responsive.css
|
|-- App.jsx                Main app shell
|-- App_old.jsx            Legacy backup kept out of the app entry path
`-- main.jsx               React entry point
```

## Notes

- Update navigation labels and destinations in `src/data/navigation.js`.
- Update featured cards and research updates in `src/data/articles.js`.
- Keep component logic small and push display copy into the data files when possible.
- `App_old.jsx` is not part of the current app bundle.
