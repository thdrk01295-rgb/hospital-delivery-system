import { StrictMode } from 'react'
import { createRoot }  from 'react-dom/client'
import { BrowserRouter } from 'react-router-dom'
import App from './App'

// One-time migration: remove the legacy shared-auth localStorage keys that the
// old single-store implementation wrote.  Only these three specific keys are
// removed; nothing else in localStorage is touched.
;['amr_token', 'amr_role', 'amr_bed_code'].forEach((k) => localStorage.removeItem(k))

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <BrowserRouter>
      <App />
    </BrowserRouter>
  </StrictMode>,
)
