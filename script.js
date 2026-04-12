function generateSidebar() {
  const activePage = document.querySelector('.page.active');
  const sidebar = document.getElementById('sidebar');
  
  if (!activePage) {
    sidebar.innerHTML = '<p>No page selected</p>';
    return;
  }
  
  const headers = activePage.querySelectorAll('h1, h2, h3');
  
  if (headers.length === 0) {
    sidebar.innerHTML = '<p>No headings found</p>';
    return;
  }
  
  let sidebarHTML = `
    <div onclick="location.reload()" style="cursor:pointer; padding:8px; background:#f0f0f0; border-radius:4px; margin-bottom:15px; text-align:center;">
      Back
    </div>
    <ul class="sidebar-nav">
  `;
  
  let currentH1 = null;
  let h1Index = 0;
  
  headers.forEach(header => {
    if (!header.id) {
      header.id = `header-${Math.random().toString(36).substr(2, 9)}`;
    }
    
    if (header.tagName === 'H1') {
      // Close previous H1 section if exists
      if (currentH1 !== null) {
        sidebarHTML += `</ul></li>`;
      }
      
      // Start new H1 section
      sidebarHTML += `
        <li class="sidebar-h1-item">
          <div class="sidebar-h1-toggle" data-h1-index="${h1Index}">
            <span class="toggle-icon">▶</span>
            <a href="#${header.id}" class="sidebar-link sidebar-link-h1" data-target="${header.id}">
              ${header.textContent}
            </a>
          </div>
          <ul class="sidebar-submenu" data-h1-submenu="${h1Index}" style="display: none;">
      `;
      
      currentH1 = header.textContent;
      h1Index++;
    } 
    else if (header.tagName === 'H2' || header.tagName === 'H3') {
      // Add H2 or H3 to current submenu
      let indent = header.tagName === 'H2' ? 20 : 40;
      let size = header.tagName === 'H2' ? 12 : 10;
      
      sidebarHTML += `
        <li style="margin-left: ${indent}px;">
          <a href="#${header.id}" class="sidebar-link sidebar-link-sub" data-target="${header.id}" style="font-size: ${size}px;">
            ${header.textContent}
          </a>
        </li>
      `;
    }
  });
  
  // Close the last H1 section
  if (currentH1 !== null) {
    sidebarHTML += `</ul></li>`;
  }
  
  sidebarHTML += '</ul>';
  sidebar.innerHTML = sidebarHTML;
  
  // Add toggle functionality
  document.querySelectorAll('.sidebar-h1-toggle').forEach(toggle => {
    toggle.addEventListener('click', function(e) {
      // Don't trigger if clicking on the actual link
      if (e.target.tagName === 'A') return;
      
      const h1Index = this.getAttribute('data-h1-index');
      const submenu = document.querySelector(`.sidebar-submenu[data-h1-submenu="${h1Index}"]`);
      const icon = this.querySelector('.toggle-icon');
      
      if (submenu.style.display === 'none') {
        submenu.style.display = 'block';
        icon.textContent = '▼';
      } else {
        submenu.style.display = 'none';
        icon.textContent = '▶';
      }
    });
  });
  
  // Add click handlers for smooth scrolling
  document.querySelectorAll('.sidebar-link').forEach(link => {
    link.addEventListener('click', function(e) {
      e.preventDefault();
      const targetId = this.getAttribute('data-target');
      const targetElement = document.getElementById(targetId);
      if (targetElement) {
        targetElement.scrollIntoView({ behavior: 'smooth', block: 'start' });
      }
    });
  });
}

async function openPage(page) {
  const container = document.getElementById('content-area');
  
  // Remove any existing pages
  const existingPages = document.querySelectorAll('.page');
  existingPages.forEach(p => p.remove());
  
  try {
    // Load the page from external file
    const response = await fetch(`pages/${page}.html`);
    if (!response.ok) throw new Error('Page not found');
    const html = await response.text();
    
    // Create new page div
    const newPage = document.createElement('div');
    newPage.id = page;
    newPage.className = 'page active';  // Directly set as active
    newPage.style.display = 'block';
    newPage.innerHTML = html;
    
    // Add to container
    container.appendChild(newPage);
    
    // Generate sidebar from loaded content
    generateSidebar();
    
  } catch (error) {
    container.innerHTML = `<div class="error">Error loading ${page}: ${error.message}</div>`;
  }
}

window.onload = function() {
  openPage('home'); // Load first page by default
};
