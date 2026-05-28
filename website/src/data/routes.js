export const routeTitles = {
  "/":                "Mensor | Subsea AI Lab",
  "/changelog":       "Mensor | Changelog",
  "/missions":        "Mensor | Missions",
  "/research":        "Mensor | Research",
  "/dolphin-explorer":"Mensor | Dolphin Explorer",
  "/about":           "Mensor | About",
  "/contact":         "Mensor | Contact",
  "/404":             "Mensor | Not Found",
};

export function normalizePath(pathname) {
  if (!pathname || pathname === "/index.html") return "/";
  if (pathname.length > 1 && pathname.endsWith("/"))
    return pathname.slice(0, -1);
  return pathname;
}
